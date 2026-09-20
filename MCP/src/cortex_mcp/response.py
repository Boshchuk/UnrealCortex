"""Response size guard for MCP tool results."""

import json
import logging

logger = logging.getLogger(__name__)

_MAX_RESPONSE_CHARS = 40_000
_MIN_LIST_SIZE = 10


def _find_largest_list(data: dict) -> str | None:
    """Find the key of the largest list with _MIN_LIST_SIZE+ items in data."""
    best_key = None
    best_len = 0
    for key, value in data.items():
        if isinstance(value, list) and len(value) >= _MIN_LIST_SIZE and len(value) > best_len:
            best_len = len(value)
            best_key = key
    return best_key


def format_response(data: dict, tool_name: str) -> str:
    """Serialize data to JSON, truncating array results if over size limit.

    If the response exceeds _MAX_RESPONSE_CHARS and contains a list with
    10+ items, binary-searches for the max item count that fits and
    appends _truncated metadata.

    For mutation results containing remaining_bindings, essential outcome
    fields are always preserved, and remaining_bindings is truncated with
    dedicated truncation metadata and referral instructions to umg.list_animation_bindings.

    Args:
        data: The response data dict.
        tool_name: Name of the tool for error messages.

    Returns:
        JSON string, guaranteed under _MAX_RESPONSE_CHARS.
    """
    text = json.dumps(data, indent=2)
    if len(text) <= _MAX_RESPONSE_CHARS:
        return text

    # Special handling for UMG animation binding mutation results:
    # Essential outcome fields must never be replaced with generic errors.
    if "remaining_bindings" in data and isinstance(data["remaining_bindings"], list):
        bindings = data["remaining_bindings"]
        original_total = data.get("_remaining_bindings_total", len(bindings))
        suggestion = "Use umg.list_animation_bindings to view the complete remaining bindings list."

        # Binary search for max remaining_bindings count that fits
        lo, hi = 0, len(bindings)
        best = -1
        best_candidate = None

        while lo <= hi:
            mid = (lo + hi) // 2
            trial = dict(data)
            trial["remaining_bindings"] = bindings[:mid]
            trial["_remaining_bindings_truncated"] = True
            trial["_remaining_bindings_total"] = original_total
            trial["_remaining_bindings_returned"] = mid
            trial["_suggestion"] = suggestion
            trial["_remaining_bindings_instructions"] = suggestion
            trial_text = json.dumps(trial, indent=2)
            if len(trial_text) <= _MAX_RESPONSE_CHARS:
                best = mid
                best_candidate = trial
                lo = mid + 1
            else:
                hi = mid - 1

        if best_candidate is not None:
            logger.info(
                "Truncated remaining_bindings response for %s: %d -> %d items",
                tool_name, len(bindings), best,
            )
            return json.dumps(best_candidate, indent=2)

        # If even 0 remaining_bindings didn't fit, check if diagnostics can be truncated
        if "diagnostics" in data and isinstance(data["diagnostics"], list):
            diag = data["diagnostics"]
            lo, hi = 0, len(diag)
            best_diag_candidate = None
            while lo <= hi:
                mid = (lo + hi) // 2
                trial = dict(data)
                trial["remaining_bindings"] = []
                trial["_remaining_bindings_truncated"] = True
                trial["_remaining_bindings_total"] = original_total
                trial["_remaining_bindings_returned"] = 0
                trial["_suggestion"] = suggestion
                trial["_remaining_bindings_instructions"] = suggestion
                trial["diagnostics"] = diag[:mid]
                trial_text = json.dumps(trial, indent=2)
                if len(trial_text) <= _MAX_RESPONSE_CHARS:
                    best_diag_candidate = trial
                    lo = mid + 1
                else:
                    hi = mid - 1

            if best_diag_candidate is not None:
                return json.dumps(best_diag_candidate, indent=2)

        # Fallback to minimal essential outcome envelope
        essential_keys = {
            "asset_path", "animation_name", "dry_run", "changed", "save_attempted", "saved",
            "fingerprint", "matched_selector", "before", "after", "scene_data_removed", "save_error",
        }
        outcome = {k: v for k, v in data.items() if k in essential_keys}
        outcome["remaining_bindings"] = []
        outcome["_remaining_bindings_truncated"] = True
        outcome["_remaining_bindings_total"] = original_total
        outcome["_remaining_bindings_returned"] = 0
        outcome["_suggestion"] = suggestion
        outcome["_remaining_bindings_instructions"] = suggestion
        return json.dumps(outcome, indent=2)

    # Auto-detect: find the largest list with 10+ items
    array_key = _find_largest_list(data)

    if array_key is None:
        logger.warning(
            "Response for %s is %d chars with no truncatable array",
            tool_name, len(text),
        )
        return json.dumps({
            "_error": "RESPONSE_TOO_LARGE",
            "_size": len(text),
            "_suggestion": "Pass 'limit' parameter to paginate through results.",
        }, indent=2)

    original_count = len(data[array_key])

    # Binary search for max count that fits
    lo, hi = 0, original_count
    best = 0
    while lo <= hi:
        mid = (lo + hi) // 2
        trial = dict(data)
        trial[array_key] = data[array_key][:mid]
        trial["_truncated"] = {
            "original_count": original_count,
            "returned_count": mid,
            "suggestion": "Pass 'limit' parameter to paginate through results.",
        }
        trial_text = json.dumps(trial, indent=2)
        if len(trial_text) <= _MAX_RESPONSE_CHARS:
            best = mid
            lo = mid + 1
        else:
            hi = mid - 1

    truncated = dict(data)
    truncated[array_key] = data[array_key][:best]
    truncated["_truncated"] = {
        "original_count": original_count,
        "returned_count": best,
        "suggestion": "Pass 'limit' parameter to paginate through results.",
    }

    logger.info(
        "Truncated %s response for %s: %d -> %d items",
        array_key, tool_name, original_count, best,
    )
    return json.dumps(truncated, indent=2)


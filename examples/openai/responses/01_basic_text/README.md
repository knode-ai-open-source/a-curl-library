# 01 · Basic Text Examples

Tiny, blocking `/v1/responses` snippets that demonstrate the most common knobs
you’ll touch when **all you need back is plain text**.  
Build everything in one shot:

```bash
cd examples/openai/responses/01_basic_text
./build.sh          # ⇒ builds every target listed below
````

| Executable                          | What it demonstrates                                                                 |
| ----------------------------------- | ------------------------------------------------------------------------------------ |
| **basic\_text**                     | Smallest-possible request ➜ prints full answer + token usage.                        |
| **basic\_text\_parallel**           | Fires 5 prompts in parallel; each runs its own callback.                             |
| **basic\_text\_cache\_miss**        | Shows prompt-cache key: first call (miss) then instant second call (hit).            |
| **basic\_text\_logprobs**           | Adds `top_logprobs=5` and includes the log-probabilities section in the response.    |
| **basic\_text\_error**              | Tries a bogus model id → exercises the error‐handler path.                           |
| **basic\_text\_chain**              | Two-step chain: second request passes `previous_response_id` from the first.         |
| **basic\_text\_temperature\_sweep** | Sends the same prompt at T = {0.0, 0.4, 0.7, 1.0} to compare creativity levels.      |
| **basic\_text\_instructions**       | Adds a “pirate slang” system instruction before the user prompt.                     |
| **basic\_text\_max\_tokens**        | Hard-caps completion to 32 tokens; prints usage so you can verify truncation worked. |

All examples rely on the `OPENAI_API_KEY` environment variable.

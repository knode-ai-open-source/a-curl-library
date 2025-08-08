# 02 · Structured Output Examples

Five quick demos showing how to request, parse, and even **chain** OpenAI
structured-JSON responses with the `/v1/responses` endpoint.

```bash
cd examples/openai/responses/02_structured_output
./build.sh          # builds everything below
````

| Executable                        | What it shows (all blocking)                                                   |
| --------------------------------- | ------------------------------------------------------------------------------ |
| **structured\_single\_object**    | Ask for *today’s Paris weather* as `{city,tempC,conditions}` → parse & print.  |
| **structured\_array**             | Get an **array** of the five tallest buildings `{name,height_m,city}`.         |
| **structured\_nested**            | Request a **nested project plan** (phase → tasks → owner).                     |
| **structured\_chain\_followup**   | 1️⃣ Get an array of blog-topic ideas → 2️⃣ spin a **follow-up call per idea**. |
| **structured\_validation\_error** | Provide an ultra-strict schema, then show the **schema-violation error**.      |

All programs require `OPENAI_API_KEY` in the environment.

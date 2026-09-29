Feature: LLM composition assistant
  A producer describes what they want in musical words — "a minor pentatonic
  ascending", "offbeat hats", "make this jazzier" — and the assistant has a
  model write it into the open pattern. The model sees the session it is
  writing for (tuning, scale, tempo, meter, what is already there), its
  answer is checked like any other input, and nothing reaches the song until
  the producer applies it. The producer picks where prompts are answered:
  a local model (Ollama) by default, or a cloud model they have keyed
  (Gemini, MiniMax, ChatGPT or OpenRouter). Tests never touch a network:
  canned replies stand in for the model.

  Scenario: A reply in the wire dialect becomes triggers
    Given a reply holding valid JSON with mode "replace" and three triggers
    When the reply is parsed for a sixteen-step pattern
    Then parsing succeeds and yields three triggers on the right steps
    Checked by llm_parse_replace.

  Scenario: Add and modify modes are honored
    Given replies with mode "add" and mode "modify"
    When they are parsed
    Then each reports its mode
    Checked by llm_parse_add_and_modify.

  Scenario: Markdown fences and padding around the JSON are tolerated
    Given a reply that fences its JSON in a code block behind a sentence
    When the reply is parsed
    Then parsing succeeds with the fenced object
    Checked by llm_parse_fenced_and_padded.

  Scenario: Pure prose is rejected, not crashed on
    Given a reply holding no JSON at all
    When the reply is parsed
    Then parsing fails with a readable reason
    Checked by llm_parse_rejects_prose.

  Scenario: Out-of-range fields are repaired and admitted
    Given a reply whose velocities exceed 1 and whose ratchets are 0
    When the reply is parsed
    Then the values are clamped and the corrections are named
    Checked by llm_parse_clamps_and_repairs.

  Scenario: A trigger beyond the pattern's end never sounds
    Given a reply with a trigger starting past the last step
    When the reply is parsed for a sixteen-step pattern
    Then that trigger is dropped and the rest survive
    Checked by llm_parse_drops_out_of_pattern.

  Scenario: What the model is shown is what it can answer
    Given a pattern with notes and a chord
    When it is serialized to context and parsed back
    Then every step, key, velocity and voice survives the round trip
    Checked by llm_roundtrip_triggers.

  Scenario: The gate's backend answers from a script, not a server
    Given a scripted backend with a matching and a non-matching line
    When a matching prompt and an unmatched prompt are asked
    Then the first is answered and the second fails readably
    Checked by llm_scripted_backend_answers.

  Scenario: The standing instructions document the same schema the parser reads
    Given the system prompt
    When it is inspected
    Then it names the modes and every trigger field the parser accepts
    Checked by llm_prompt_documents_schema.

  Scenario Outline: Each backend builds the request its provider expects
    Given the <backend> backend with a key and a captured network
    When the producer sends a prompt after one earlier exchange
    Then the request goes to the provider's default endpoint with the key in
      its header, never in the URL, asks for a JSON-only answer, carries the
      standing instructions and the session context, and says the earlier
      exchange exactly once
    Checked by llm_<case>_backend_builds_request.

    Examples:
      | backend    | case       |
      | MiniMax    | minimax    |
      | ChatGPT    | openai     |
      | OpenRouter | openrouter |
      | Gemini     | gemini     |
      | Ollama     | ollama     |

  Scenario Outline: A backend's answer feeds the wire-dialect parser unchanged
    Given a <backend> answer whose text is a valid triggers object
    When the answer is parsed
    Then the trigger survives into the proposal
    Checked by llm_<case>_backend_parses_reply.

    Examples:
      | backend    | case       |
      | MiniMax    | minimax    |
      | ChatGPT    | openai     |
      | OpenRouter | openrouter |
      | Gemini     | gemini     |
      | Ollama     | ollama     |

  Scenario Outline: A cloud backend without its key says which to set
    Given the <backend> backend with no key
    When the producer sends a prompt
    Then the reply is not ok and the error names <variable>
    And the backend is still offered by the picker
    Checked by llm_<case>_backend_missing_key_errors.

    Examples:
      | backend    | case       | variable                |
      | MiniMax    | minimax    | BLOKKILY_MINIMAX_KEY    |
      | ChatGPT    | openai     | BLOKKILY_OPENAI_KEY     |
      | OpenRouter | openrouter | BLOKKILY_OPENROUTER_KEY |
      | Gemini     | gemini     | BLOKKILY_GEMINI_KEY     |

  Scenario Outline: A transport failure names the backend that failed
    Given the <backend> backend whose connection is refused
    When the producer sends a prompt
    Then the reply is not ok and the error says <backend> did not answer
    Checked by llm_<case>_backend_network_error_is_readable.

    Examples:
      | backend    | case       |
      | MiniMax    | minimax    |
      | ChatGPT    | openai     |
      | OpenRouter | openrouter |
      | Gemini     | gemini     |
      | Ollama     | ollama     |

  Scenario Outline: The provider's own refusal reaches the producer
    Given the <backend> backend whose provider answers HTTP 401 with its
      explanation in the body
    When the producer sends a prompt
    Then the reply is not ok and the error carries the provider's message,
      not only the HTTP status
    Checked by llm_<case>_backend_server_error_is_readable.

    Examples:
      | backend    | case       |
      | MiniMax    | minimax    |
      | ChatGPT    | openai     |
      | OpenRouter | openrouter |
      | Gemini     | gemini     |
      | Ollama     | ollama     |

  Scenario Outline: The URL and model variables override the defaults
    Given the <backend> backend with a custom URL and model in the environment
    When the producer sends a prompt
    Then the request goes to the custom URL with the custom model
    And the picker names the custom model
    Checked by llm_<case>_backend_uses_custom_url_and_model.

    Examples:
      | backend    | case       |
      | MiniMax    | minimax    |
      | ChatGPT    | openai     |
      | OpenRouter | openrouter |
      | Ollama     | ollama     |

  Scenario: A proposal is previewed, applied, and undone — end to end
    Given the application with a scripted backend
    When the producer types a prompt in the prompt bar, sees the proposal,
      applies it, and then undoes
    Then the proposal waits for Apply, the pattern and every editor show the
      written triggers after Apply, and undo returns the pattern to what it
      was — the whole generation being one step of history
    Checked by bdd_llm_assistant (verify scenario "llm"), which also saves
    build/artifacts/llm-assistant.png and three phase captures
    (llm-assistant-idle.png, llm-assistant-proposal.png, and the end frame).

  Scenario: A proposal can be discarded without touching the pattern
    Given the application with a scripted backend holding a proposal
    When the producer clicks Discard
    Then the proposal is dropped and the pattern is unchanged
    Checked by bdd_llm_assistant.

  Scenario: The bar's idle state is honestly idle
    Given the application before any prompt
    When the prompt bar is read
    Then there is no proposal, no Apply or Discard buttons, and the status
      names the chosen backend
    Checked by bdd_llm_assistant, which also saves
    build/artifacts/llm-assistant-idle.png.

  Scenario: A held proposal is visibly a proposal
    Given a proposal has just arrived
    When the prompt bar is read
    Then the status names what was proposed, and Apply and Discard are
      visible and usable
    Checked by bdd_llm_assistant, which also saves
    build/artifacts/llm-assistant-proposal.png.

  Scenario: Switching the backend drops any held proposal and re-arms the bar
    Given the application with a proposal on screen
    When the producer switches the backend picker
    Then the proposal is dropped, Apply and Discard disappear, and the
      status names the new backend
    Checked by bdd_llm_assistant.

  Scenario: Switching the backend while a prompt is on its way unlocks the bar
    Given the application waiting on an answer to a prompt
    When the producer switches the backend
    Then the bar is no longer busy and the prompt field accepts input
    And the old backend's answer never becomes a proposal
    Checked by bdd_llm_assistant.

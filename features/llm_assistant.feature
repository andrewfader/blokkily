Feature: LLM composition assistant
  A producer describes what they want in musical words — "a minor pentatonic
  ascending", "offbeat hats", "make this jazzier" — and the assistant has a
  model write it into the open pattern. The model sees the session it is
  writing for (tuning, scale, tempo, meter, what is already there), its
  answer is checked like any other input, and nothing reaches the song until
  the producer applies it. The producer picks where prompts are answered:
  a local model (Ollama) by default, or a cloud model (Gemini) they have
  keyed. Tests never touch a network: canned replies stand in for the model.

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

  Scenario: The MiniMax backend builds the OpenAI chat-completions request
    Given a MiniMax backend with a key and a captured network access manager
    When the producer sends a prompt
    Then the request hits /v1/chat/completions with a Bearer key, the default
      model id, a system message, and response_format json_object
    Checked by llm_minimax_backend_builds_request.

  Scenario: A MiniMax reply feeds the wire-dialect parser unchanged
    Given a MiniMax reply whose content is a valid triggers object
    When the reply is parsed
    Then the trigger survives into the proposal
    Checked by llm_minimax_backend_parses_reply.

  Scenario: Missing MiniMax key means a readable error, not silence
    Given a MiniMax backend with no key
    When the producer sends a prompt
    Then the reply is not ok and the error names BLOKKILY_MINIMAX_KEY
    Checked by llm_minimax_backend_missing_key_errors.

  Scenario: A MiniMax transport failure surfaces readably
    Given a MiniMax backend whose network refused the connection
    When the producer sends a prompt
    Then the reply is not ok and the error mentions MiniMax
    Checked by llm_minimax_backend_network_error_is_readable.

  Scenario: A MiniMax server-side error envelope reaches the producer
    Given a MiniMax reply holding {"error":{"message":…}}
    When the producer sends a prompt
    Then the reply is not ok and the error carries the provider's message
    Checked by llm_minimax_backend_server_error_is_readable.

  Scenario: BLOKKILY_MINIMAX_URL and _MODEL override the defaults
    Given a MiniMax backend with custom URL and model env vars
    When the producer sends a prompt
    Then the request URL and the body's model field honour the overrides
    Checked by llm_minimax_backend_uses_custom_url_and_model.

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

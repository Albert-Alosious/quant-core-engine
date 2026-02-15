# Development Rules for LLM Collaboration

1. Never generate toy examples.
2. Never collapse multiple modules into one file.
3. Never introduce global mutable state.
4. Never allow strategy to call execution directly.
5. All communication must be event-based.
6. Keep changes small and incremental.
7. Always explain architectural decisions before writing code.
8. Never refactor large portions without explicit instruction.
9. Always maintain clean folder boundaries.
10. Code must be production-style, not tutorial-style.
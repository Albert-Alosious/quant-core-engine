Context:
The developer has strong embedded C experience and limited modern C++ experience.
The goal is to build a production-quality trading engine while making the code educational.

Global Coding Rules:

1. Every class must have a detailed header comment explaining:
   - Its responsibility
   - Why it exists in the architecture
   - Which thread it runs on (if applicable)

2. Every method/function must include:
   - What it does
   - Why it exists
   - Thread-safety assumptions
   - Input/output behavior

3. Important lines must have inline comments explaining:
   - Why the line exists
   - Any modern C++ construct used
   - Any ownership or lifetime behavior

4. When using modern C++ features (e.g. std::variant, std::visit, move semantics,
   std::lock_guard, RAII, lambdas), add short explanatory comments describing:
   - What the feature does
   - Why it is used instead of a C-style approach

5. Avoid overly clever or condensed code.
   Prefer clarity and step-by-step logic.

6. Do not remove educational comments unless explicitly instructed.

7. Keep the code production-quality, thread-safe, and aligned with
   docs/architecture.md and docs/development_rules.md.
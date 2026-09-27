// The human side of "tools that change the project ask first" (TECH_DECISIONS
// §28). One broker per chat session: a tool call that mutates blocks on a
// promise that only the UI's approval_response message can resolve.
import { randomUUID } from "node:crypto";

export type ApprovalDecision = "allow_once" | "allow_always" | "deny";

export interface PendingApproval {
  id: string;
  toolName: string;
  input: Record<string, unknown>;
}

// A UI that never responds must not hang a session forever -- 10 minutes is
// long enough for a human to notice and short enough to eventually free the
// SDK worker. Denial (not a crash) is the safe failure mode here.
const APPROVAL_TIMEOUT_MS = 10 * 60 * 1000;

export class ApprovalBroker {
  private readonly alwaysAllowed = new Set<string>();
  private readonly pending = new Map<
    string,
    { resolve: (d: ApprovalDecision) => void; timer: ReturnType<typeof setTimeout> }
  >();

  constructor(private readonly onRequest: (req: PendingApproval) => void) {}

  /** Resolves once the tool call is allowed to run, or throws if denied. */
  async requestApproval(toolName: string, input: Record<string, unknown>): Promise<void> {
    if (this.alwaysAllowed.has(toolName)) return;

    const id = randomUUID();
    const decision = await new Promise<ApprovalDecision>((resolve) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        resolve("deny");
      }, APPROVAL_TIMEOUT_MS);
      this.pending.set(id, { resolve, timer });
      this.onRequest({ id, toolName, input });
    });

    if (decision === "allow_always") this.alwaysAllowed.add(toolName);
    if (decision === "deny") throw new ApprovalDenied(toolName);
  }

  resolve(id: string, decision: ApprovalDecision): boolean {
    const entry = this.pending.get(id);
    if (!entry) return false;
    clearTimeout(entry.timer);
    this.pending.delete(id);
    entry.resolve(decision);
    return true;
  }

  /** So a disconnecting UI doesn't leave a tool call hanging until the timeout. */
  denyAllPending(): void {
    for (const id of [...this.pending.keys()]) this.resolve(id, "deny");
  }
}

export class ApprovalDenied extends Error {
  constructor(public toolName: string) {
    super(`"${toolName}" was not approved`);
  }
}

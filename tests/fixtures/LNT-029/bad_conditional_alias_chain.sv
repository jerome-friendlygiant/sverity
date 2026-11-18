// sverity-args: --top top
// Conditional next-state values and two direct alias hops retain per-FSM dead-state obligations.
// expect: LNT-029 WARNING
// expect: LNT-071 WARNING
module top(input logic clk, go);
  typedef enum logic [1:0] {IDLE, RUN, DONE, NEVER} state_t;
  state_t state, middle, next;
  always_ff @(posedge clk) state <= middle;
  always_comb begin
    next = state;
    case (state)
      IDLE: next = go ? RUN : IDLE;
      RUN: next = DONE;
      DONE: next = IDLE;
    endcase
    middle = next;
  end
endmodule

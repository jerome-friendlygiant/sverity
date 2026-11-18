// sverity-args: --top top
// An opaque computed next-state value makes state reachability uncertain and is explicitly reported unresolved.
// expect: LNT-029 WARNING
module top(input logic clk, input logic [1:0] raw);
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
  state_t state, next_state;
  always_ff @(posedge clk) state <= next_state;
  always_comb begin
    next_state = state_t'(raw);
    case (state)
      IDLE: next_state = RUN;
      RUN: next_state = DONE;
      DONE: next_state = IDLE;
    endcase
  end
endmodule

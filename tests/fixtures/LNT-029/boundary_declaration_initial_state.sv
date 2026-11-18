// sverity-args: --top top
// A declaration initializer is an assigned value (the entry state of the graph), though ASIC synthesis requires separate review.
// expect-none: LNT-029
// expect-none: LNT-071
module top(input logic clk);
  typedef enum logic [1:0] {INIT, IDLE, RUN} state_t;
  state_t state=INIT, next_state;
  always_ff @(posedge clk) state<=next_state;
  always_comb begin
    next_state=state;
    case(state)
      INIT: next_state=IDLE;
      IDLE: next_state=RUN;
      RUN: next_state=IDLE;
    endcase
  end
endmodule

// sverity-args: --top top
// The output decoder shares the state enum but writes no state-related symbol, and the real state machine has a proven transition graph.
// expect-none: LNT-071
// expect-none: LNT-029 WARNING
module top(input logic clk, rst_n, input logic sel, output logic y);
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
  state_t state, next_state, mode;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) state <= IDLE;
    else state <= next_state;
  always_comb begin
    next_state = state;
    case (state)
      IDLE: next_state = sel ? RUN : IDLE;
      RUN: next_state = DONE;
      DONE: next_state = IDLE;
      default: next_state = IDLE;
    endcase
  end
  always_comb begin
    case (mode)
      IDLE: y = 1'b0;
      RUN: y = 1'b1;
      default: y = 1'b0;
    endcase
  end
endmodule

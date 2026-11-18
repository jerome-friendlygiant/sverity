// sverity-args: --top top
// A selector converted from raw bits is not a state symbol, yet its arms write the enum type.
// expect: LNT-029 WARNING 10
module top(input logic clk, input logic rst_n, input logic [1:0] raw);
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
  state_t state;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) state <= IDLE;
    else
      case (state_t'(raw))
        IDLE: state <= RUN;
        RUN: state <= DONE;
        default: ;
      endcase
endmodule

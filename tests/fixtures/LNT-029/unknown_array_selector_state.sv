// sverity-args: --top top
// A state held in an array element, selected by a dynamic index, is not attributed to one state symbol.
// expect: LNT-029 WARNING 11
// expect: LNT-071 WARNING 11
module top(input logic clk, input logic rst_n, input logic i);
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
  state_t st [2];
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin st[0] <= IDLE; st[1] <= IDLE; end
    else
      case (st[i])
        IDLE: st[i] <= RUN;
        RUN: st[i] <= DONE;
        default: ;
      endcase
endmodule

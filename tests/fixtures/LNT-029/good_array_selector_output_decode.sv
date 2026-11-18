// sverity-args: --top top
// An array-element state decoded into a different type is not transition logic.
// expect-none: LNT-029
module top(input logic clk, input logic rst_n, input logic i, output logic busy);
  typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
  state_t st [2];
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) begin st[0] <= IDLE; st[1] <= IDLE; end
    else st[i] <= RUN;
  always_comb
    case (st[i])
      IDLE: busy = 1'b0;
      default: busy = 1'b1;
    endcase
endmodule

// sverity-args: --top top
// The next-state logic sits in a child; the register that holds the state is in its parent.
// expect: LNT-029 WARNING 8
typedef enum logic [1:0] {IDLE, RUN, DONE} state_t;
module next_logic(input state_t cur, output state_t nxt);
  always_comb begin
    nxt = cur;
    case (cur)
      IDLE: nxt = RUN;
      RUN: nxt = DONE;
      default: ;
    endcase
  end
endmodule
module top(input logic clk, input logic rst_n);
  state_t state, nxt;
  next_logic u(.cur(state), .nxt(nxt));
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) state <= IDLE; else state <= nxt;
endmodule

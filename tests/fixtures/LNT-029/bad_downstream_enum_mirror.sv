// sverity-args: --top top
// A downstream observation cannot make NEVER reachable in the state register.
// expect: LNT-029 WARNING
module top(input logic clk, rst_n, go);
  typedef enum logic [1:0] {IDLE, RUN, NEVER} state_t;
  state_t state, next_state, mirror;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) state <= IDLE; else state <= next_state;
  always_comb begin
    next_state = state;
    case (state)
      IDLE: next_state = RUN;
      RUN: next_state = IDLE;
      default: next_state = IDLE;
    endcase
  end
  always_comb begin mirror=state; if(go) mirror=NEVER; end
endmodule

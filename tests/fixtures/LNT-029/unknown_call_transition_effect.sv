// sverity-args: --top top
// A captured next-state write in a user call must not disappear as an output-only decoder.
// expect: LNT-029 WARNING
// expect: LNT-071 WARNING
module top(input logic clk);
  typedef enum logic {IDLE, RUN} state_t;
  state_t state, next_state;
  function automatic void advance(); next_state=RUN; endfunction
  always_ff @(posedge clk) state<=next_state;
  always_comb begin
    next_state=state;
    case(state)
      IDLE: advance();
      RUN: advance();
    endcase
  end
endmodule

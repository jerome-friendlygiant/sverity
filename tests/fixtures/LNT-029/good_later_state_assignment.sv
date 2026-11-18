// sverity-args: --top top
// A later related write can provide an exit; missing case-arm writes do not prove a stuck state.
// expect-none: LNT-029
// expect: LNT-071 WARNING
module top(input logic clk, go, output logic y);
  typedef enum logic {IDLE, RUN} state_t;
  state_t state, next_state;
  always_ff @(posedge clk) state<=next_state;
  always_comb begin
    y=0;
    case(state)
      IDLE: next_state=RUN;
      RUN: y=1;
      default: next_state=IDLE;
    endcase
    next_state=go ? IDLE : RUN;
  end
endmodule

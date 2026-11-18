// sverity-args: --top top
// This case decodes outputs; an independent next-state update follows it.
// expect-none: LNT-029
// expect-none: LNT-071
module top(input logic clk, go, output logic y);
  typedef enum logic {IDLE, RUN} state_t;
  state_t state, next_state;
  always_ff @(posedge clk) state <= next_state;
  always_comb begin
    case(state)
      IDLE: y=0;
      RUN: y=1;
    endcase
    next_state=go ? RUN : IDLE;
  end
endmodule

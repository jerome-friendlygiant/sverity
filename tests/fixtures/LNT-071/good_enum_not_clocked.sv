// sverity-args: --top top
// Near-miss of obligation_fsm: the enum type is assigned only in combinational logic, so LNT-029 does not examine this case.
// expect-none: LNT-071
module top(input logic [1:0] sel, output logic y);
  typedef enum logic [1:0] {IDLE, RUN, DONE} mode_t;
  mode_t m;
  always_comb begin
    m = mode_t'(sel);
    case (m)
      IDLE: y = 1'b0;
      RUN: y = 1'b1;
      default: y = 1'b0;
    endcase
  end
endmodule

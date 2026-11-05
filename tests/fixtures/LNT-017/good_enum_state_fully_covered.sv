// sverity-args: --top top
// Near-miss of bad_enum_state_not_covered: every enumerator of st_t has an arm, DONE included.
// expect-none: LNT-017
module top(input logic [1:0] sel, output logic y);
  typedef enum logic [1:0] {IDLE, RUN, DONE} st_t;
  st_t st;
  assign st = st_t'(sel);
  always_comb begin
    y = 1'b0;
    case (st)
      IDLE: y = 1'b0;
      RUN: y = 1'b1;
      DONE: y = 1'b1;
    endcase
  end
endmodule

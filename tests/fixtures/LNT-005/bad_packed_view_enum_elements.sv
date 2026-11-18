// sverity-args: --top top
// Raw bits into enum elements bypass the enum's value check.
// revert: state_e [3:0] states => logic [7:0] states
// revert-expect-none: LNT-005 WARNING
// expect: LNT-005 WARNING 9
module top(input logic [7:0] raw, output logic y);
  typedef enum logic [1:0] {A, B, C, D} state_e;
  state_e [3:0] states;
  assign states = raw;
  assign y = (states[0] == A);
endmodule

// sverity-args: --top top
// 4-state data into a 2-state view: X and Z become 0 in simulation.
// revert: output bit [3:0][7:0] two_state => output logic [3:0][7:0] two_state
// revert-expect-none: LNT-005 WARNING
// expect: LNT-005 WARNING 7
module top(input logic [31:0] x, output bit [3:0][7:0] two_state);
  assign two_state = x;
endmodule

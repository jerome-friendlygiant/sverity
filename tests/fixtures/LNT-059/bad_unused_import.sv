// sverity-args: --top top
// expect: LNT-059 WARNING
package pkg; localparam int W = 4; endpackage
module top(input logic a, output logic y);
  import pkg::*;
  assign y = a;
endmodule

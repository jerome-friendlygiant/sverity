// sverity-args: --top top
// Near-miss of the undeclared-identifier cases: the names resolve through a wildcard import and a package-qualified reference.
// expect-none: LNT-002
package cfg_pkg;
  localparam logic EN = 1'b1;
endpackage
module top(input logic a, output logic y);
  import cfg_pkg::*;
  assign y = a & EN & cfg_pkg::EN;
endmodule

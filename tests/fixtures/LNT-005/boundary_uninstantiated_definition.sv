// sverity-args: --top top
// A definition no instance uses is elaborated by slang with its parameters unresolved, so the constant conversion below is slang's
// finding on a module that is not hardware of this run: INFO, not proven and not dropped, until a top uses the module.
// expect: LNT-005 INFO 15
// expect-none: LNT-005 WARNING
module top(input logic a, output logic y);
  assign y = a;
endmodule
package util_pkg;
  function automatic integer vbits(integer value);
    return (value == 1) ? 1 : $clog2(value);
  endfunction
endpackage
module unused #(parameter int unsigned InW = 32) (input logic [InW-1:0] d, output logic q);
  localparam int unsigned IdxW = util_pkg::vbits(InW);
  assign q = d[IdxW-1];
endmodule

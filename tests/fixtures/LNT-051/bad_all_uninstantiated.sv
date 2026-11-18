// sverity-args: --top top
// expect: LNT-051 INFO 4
module top #(parameter bit WIDE = 0) (input logic a, output logic y);
  if (WIDE) begin : wide
    assign y = a;
  end
  assign y = 1'b0;
endmodule
// revert: parameter bit WIDE = 0 => parameter bit WIDE = 1
// revert-expect-none: LNT-051

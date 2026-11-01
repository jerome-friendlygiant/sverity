// sverity-args: --top top
// expect: LNT-051 WARNING 4
module top #(parameter bit WIDE = 0) (input logic a, output logic y);
  if (WIDE) begin : wide
    assign y = a;
  end
  assign y = 1'b0;
endmodule

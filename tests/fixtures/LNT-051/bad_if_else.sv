// sverity-args: --top top
// expect: LNT-051 INFO 6
module top #(parameter bit WIDE = 1) (input logic a, output logic y);
  if (WIDE) begin : wide
    assign y = a;
  end else begin : narrow
    assign y = ~a;
  end
endmodule

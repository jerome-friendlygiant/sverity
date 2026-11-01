// sverity-args: --top top
// expect-count: LNT-051 INFO 2
module leaf #(parameter bit WIDE = 1) (input logic a, output logic y);
  if (WIDE) begin : wide
    assign y = a;
  end else begin : narrow
    assign y = ~a;
  end
endmodule
module top(input logic a, output logic y0, output logic y1);
  leaf #(.WIDE(1)) u0 (.a(a), .y(y0));
  leaf #(.WIDE(0)) u1 (.a(a), .y(y1));
endmodule

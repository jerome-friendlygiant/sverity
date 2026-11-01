// sverity-args: --top top
// expect: LNT-033 ERROR 9
// expect-count: LNT-033 ERROR 2
module leaf(input logic [1:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    unique casez (s)
      2'b1?: y = 1'b1;
      2'b11: y = 1'b0;
      default: y = 1'b0;
    endcase
  end
endmodule
module top(input logic [1:0] s, input logic a, output logic y0, output logic y1);
  leaf u0 (.s(s), .y(y0));
  leaf u1 (.s(s), .y(y1));
endmodule

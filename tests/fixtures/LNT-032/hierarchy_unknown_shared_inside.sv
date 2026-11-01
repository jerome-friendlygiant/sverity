// sverity-args: --top top
// expect-count: LNT-032 WARNING 2
// expect: LNT-032 WARNING 9
module leaf(input logic [1:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    case (s) inside
      [2'd0:2'd2]: y = 1'b1;
      [2'd1:2'd3]: y = 1'b0;
      default: y = 1'b0;
    endcase
  end
endmodule
module top(input logic [1:0] s, output logic y0, output logic y1);
  leaf u0 (.s(s), .y(y0));
  leaf u1 (.s(s), .y(y1));
endmodule

// sverity-args: --top top
// expect: LNT-033 INFO 6
module top(input logic [1:0] s, input logic [1:0] k, output logic y);
  always_comb begin
    y = 1'b0;
    unique case (s)
      k: y = 1'b1;
      default: y = 1'b0;
    endcase
  end
endmodule

// sverity-args: --top top
// expect: LNT-081 WARNING
module top(input logic [1:0] s, output logic y);
  always_comb begin
    unique case (s)
      2'b00: y = 1'b0;
    endcase
  end
endmodule

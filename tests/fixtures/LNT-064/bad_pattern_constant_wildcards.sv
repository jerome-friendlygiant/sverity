// sverity-args: --top top
// A casez pattern constant whose bits are all wildcards matches every value, as a casez item does.
// expect: LNT-064 WARNING 8
module top(input logic [3:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    casez (s) matches
      4'b????: y = 1'b1;
      4'b0001: y = 1'b0;
    endcase
  end
endmodule

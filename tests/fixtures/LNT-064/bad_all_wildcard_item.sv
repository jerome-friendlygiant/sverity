// sverity-args: --top top
// a casez item whose bits are all wildcards matches every value and shadows the items after it
// expect: LNT-064 WARNING 8
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    y = 0;
    casez (s)
      2'b??: y = 3;
      default: y = 0;
    endcase
  end
endmodule

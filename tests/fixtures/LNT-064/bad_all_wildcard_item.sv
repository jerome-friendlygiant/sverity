// revert: 2'b?? => 2'b10
// revert-expect-none: LNT-064
// sverity-args: --top top
// a casez item whose bits are all wildcards matches every value and shadows the item after it
// expect: LNT-064 WARNING 10
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    y = 0;
    casez (s)
      2'b??: y = 3;
      2'b01: y = 1;
    endcase
  end
endmodule

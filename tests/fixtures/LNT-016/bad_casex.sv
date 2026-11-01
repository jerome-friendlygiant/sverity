// sverity-args: --top top
// expect: LNT-016 WARNING 7
// expect: LNT-016 WARNING 12
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    y = 0;
    casex (s)
      2'b1x: y = 1;
      default: y = 2;
    endcase
    casez (s)
      2'b??: y = 3;                        // all-wildcard item
      default: y = 0;
    endcase
  end
endmodule

// sverity-args: --top top
// An all-wildcard item that only the default follows shadows nothing but the default: INFO. A casex X item counts; a casez X item does not.
// expect: LNT-064 INFO 9
// expect: LNT-064 INFO 13
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    y = 0;
    casez (s)
      2'b??: y = 3;
      default: y = 0;
    endcase
    casex (s)
      2'bxx: y = 1;
      default: y = 0;
    endcase
    casez (s)
      2'bxx: y = 2;
      default: y = 0;
    endcase
  end
endmodule

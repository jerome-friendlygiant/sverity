// sverity-args: --top top
// expect: LNT-040 WARNING 6
module top(input logic d, output logic y, z);
 localparam logic [1:0] UNKNOWN = 2'bx0, TRUE_VALUE = 2'bx1;
 always_comb begin
   if (UNKNOWN) y = d; else y = 0;
   if (TRUE_VALUE) z = d; else z = 0;
 end
endmodule

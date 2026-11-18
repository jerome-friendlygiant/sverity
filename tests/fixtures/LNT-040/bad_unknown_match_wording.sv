// sverity-args: --top top
// expect: LNT-040 WARNING 5
// expect: LNT-040 WARNING 9
module top(input logic a, input logic sel, output logic y, z);
 assign y = ({a, 1'b0} == 2'bx1);
 always_comb case (sel)
   1'b0: z = 0;
   1'b1: z = 1;
   1'bx: z = a;
 endcase
endmodule

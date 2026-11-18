// sverity-args: --top top
// expect-none: LNT-081
module top(input logic [2:0] op, output logic y, z, w);
 always_comb priority casez (op)
   3'b???: y = 1;
 endcase
 always_comb priority casex (op)
   3'b0??: z = 0;
   3'b1??: z = 1;
 endcase
 always_comb priority case (op) inside
   3'bxxx: w = 1;
 endcase
endmodule

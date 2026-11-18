// sverity-args: --top top
// expect: LNT-040 WARNING 5
module top(input logic d, output logic y);
 localparam logic [1:0] SELECT = 2'bx0;
 always_comb case (SELECT)
   2'b00: y = d;
   default: y = 0;
 endcase
endmodule

// sverity-args: --top top
// expect-none: LNT-081
module top(input bit [1:0] op, output logic y);
 always_comb begin y=0; unique0 case(op) 0:y=1; endcase end
endmodule

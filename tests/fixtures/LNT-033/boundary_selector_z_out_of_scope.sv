// sverity-args: --top top
// expect-none: LNT-033
// Overlap is judged over two-state values, including for released selectors.
module top(output logic y);
 wire op = 1'bz;
 always_comb unique casez (op)
   1'b0: y = 0;
   1'b1: y = 1;
 endcase
endmodule

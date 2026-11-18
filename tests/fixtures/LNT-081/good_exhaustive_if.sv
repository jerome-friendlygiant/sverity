// sverity-args: --top top
// expect-none: LNT-081
module top(input bit enable, input bit [1:0] mode, output logic y, z);
 always_comb priority if (enable) y = 1; else if (!enable) y = 0;
 always_comb priority if (mode == 0) z = 0;
   else if (mode == 1) z = 1;
   else if (mode == 2) z = 0;
   else if (mode == 3) z = 1;
endmodule

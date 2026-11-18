// sverity-args: --top top
// revert: if (sel == 2'd3) return 4'hf; => else return 4'hf;
// revert-expect-none: LNT-013
// sel == 3 is never 0, 1 or 2 here, but the else-less chain can still fall off the end: the result is not assigned.
// expect: LNT-013 WARNING 7
module top(input logic [1:0] sel, output logic [3:0] mask);
  function automatic logic [3:0] decode(input logic [1:0] s);
    if (s == 2'd0) return 4'h1;
    else if (s == 2'd1) return 4'h3;
    else if (s == 2'd2) return 4'h7;
    if (sel == 2'd3) return 4'hf;
  endfunction
  assign mask = decode(sel);
endmodule

// sverity-args: --top top
// revert: import "DPI-C" pure function int unsigned mix(input int unsigned v); => function automatic int unsigned mix(input int unsigned v); return v ^ 32'h5a5a5a5a; endfunction
// revert-expect-none: LNT-080
// A foreign function computes an output of the design: synthesis cannot build its body.
// expect: LNT-080 WARNING 7
module top(input logic [31:0] d, output logic [31:0] q);
  import "DPI-C" pure function int unsigned mix(input int unsigned v);
  assign q = mix(d);
endmodule

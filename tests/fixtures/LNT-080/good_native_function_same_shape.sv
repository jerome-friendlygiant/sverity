// sverity-args: --top top
// The same shape as a DPI import, written as a native function: nothing foreign.
// expect-none: LNT-080
module top(input logic [31:0] d, output logic [31:0] q);
  function automatic int unsigned mix(input int unsigned v);
    return v ^ 32'h5a5a5a5a;
  endfunction
  assign q = mix(d);
endmodule

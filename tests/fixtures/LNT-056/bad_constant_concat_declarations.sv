// sverity-args: --top top
// Net assignments and variable initializers have the same width contract.
// expect: LNT-056 WARNING 7
// expect: LNT-056 WARNING 8
// expect-count: LNT-056 WARNING 2
module top(output wire [3:0] x, output logic [3:0] y);
  wire [3:0] n = {4'h1, 4'h2};
  logic [3:0] v = {4'h3, 4'h4};
  assign x = n;
  assign y = v;
endmodule

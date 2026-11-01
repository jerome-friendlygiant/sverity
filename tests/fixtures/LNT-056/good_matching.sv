// sverity-args: --top top
// expect-none: LNT-056
module top(output logic [7:0] w1, w2, w3, output logic [3:0] n);
  assign w1 = {4'h1, 4'h2};         // 8 bits into 8
  assign w2 = {4{2'b10}};           // 8 bits into 8
  assign w3 = {4'hF, 4'h0};
  assign n = 4'hF;                  // a sized literal outside a concatenation is LNT-026's
endmodule

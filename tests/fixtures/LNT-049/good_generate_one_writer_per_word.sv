// sverity-args: --top top
// Each generate iteration writes its own word, so no word has two write processes: no LNT-049 finding.
// expect-none: LNT-049 WARNING
module top(input logic clk, we, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] rf [4];
  for (genvar i = 0; i < 4; i++) begin : gen_word
    always_ff @(posedge clk) if (we) rf[i] <= d;
  end
  assign q = rf[0];
endmodule

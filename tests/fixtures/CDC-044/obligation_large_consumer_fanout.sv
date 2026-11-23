// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Consumer evidence is capped; an early positive witness must not invent an omitted total.
// expect: CDC-044 WARNING
module top(input logic clka, rx, output logic [63:0] pulse);
  logic s1, s2, s3;
  always_ff @(posedge clka) begin
    s1 <= rx;
    s2 <= s1;
    s3 <= s2;
  end
  for (genvar i = 0; i < 64; i++) begin : g
    assign pulse[i] = s2 ^ s3;
  end
endmodule

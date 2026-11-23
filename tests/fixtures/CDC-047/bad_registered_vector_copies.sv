// sverity-args: --top top --sdc @DIR@/primary_input_async.sdc
// expect-count: CDC-047 WARNING 1
// revert: s1 <= {launch, launch}; => s1 <= {other, launch};
// revert-expect-none: CDC-047
module top(input logic clka, clkb, d, output logic [1:0] q);
  logic launch, other;
  logic [1:0] s1, s2;
  always_ff @(posedge clka) begin
    launch <= d;
    other <= ~d;
  end
  always_ff @(posedge clkb) begin
    s1 <= {launch, launch};
    s2 <= s1;
  end
  assign q = s2;
endmodule

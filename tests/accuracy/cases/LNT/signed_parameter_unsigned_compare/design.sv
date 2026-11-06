module temperature_guard #(
  parameter logic signed [7:0] LIMIT = -8'sd2
) (
  input  logic       enable,
  input  logic [7:0] sample,
  output logic       below_limit,
  output logic [7:0] margin
);
  always_comb begin
    below_limit = 1'b0;
    margin = 8'd0;
    if (enable && (sample < LIMIT)) begin
      below_limit = 1'b1;
      margin = LIMIT - sample;
    end
  end
endmodule

module top (
  input  logic       enable,
  input  logic [7:0] sample,
  output logic       below_limit,
  output logic [7:0] margin
);
  temperature_guard #(.LIMIT(-8'sd2)) u_guard (
    .enable(enable),
    .sample(sample),
    .below_limit(below_limit),
    .margin(margin)
  );
endmodule

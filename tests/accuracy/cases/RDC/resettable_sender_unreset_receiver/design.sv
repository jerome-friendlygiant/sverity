module top(input logic clk,
  src_reset_n,
  set_token,
  output logic observed);

  logic token;
  always_ff @(posedge clk or negedge src_reset_n) begin
    if (!src_reset_n) token <= 0;
    else if (set_token) token <= 1;
  end
  always_ff @(posedge clk) begin
    observed <= token;
  end

endmodule

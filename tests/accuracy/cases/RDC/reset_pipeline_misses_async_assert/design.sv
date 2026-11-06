module top(input logic clk,
  por_n,
  output logic running);

  logic meta_n, local_n;
  // Power-on reset must assert even while the oscillator is stopped.
  always_ff @(posedge clk or negedge por_n) begin
    if (!por_n) meta_n <= 0;
    else meta_n <= 1;
  end
  always_ff @(posedge clk) local_n <= meta_n;
  always_ff @(posedge clk or negedge local_n) begin
    if (!local_n) running <= 0;
    else running <= 1;
  end

endmodule

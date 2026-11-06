module top(input logic clk,
  por_n,
  output logic running);

  logic meta_n, settled_n;
  always_ff @(posedge clk or negedge por_n) begin
    if (!por_n) begin meta_n <= 0; settled_n <= 0; end
    else begin meta_n <= 1; settled_n <= meta_n; end
  end
  always_ff @(posedge clk or negedge meta_n) begin
    if (!meta_n) running <= 0;
    else running <= 1;
  end

endmodule

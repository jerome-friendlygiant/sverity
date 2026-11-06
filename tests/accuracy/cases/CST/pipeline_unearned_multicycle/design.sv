module top (
  input logic clk,
  input logic [15:0] input_word,
  output logic [15:0] result_q
);
  logic [15:0] source_q;
  function automatic logic [15:0] mix(input logic [15:0] word);
    logic [15:0] rotated;
    rotated = {word[4:0], word[15:5]};
    return (word + 16'h1397) ^ rotated;
  endfunction
  always_ff @(posedge clk) begin
    source_q <= input_word;
    result_q <= mix(source_q);
  end
endmodule

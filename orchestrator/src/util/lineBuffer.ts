/**
 * Accumulates raw stream chunks and yields complete lines as they become available.
 * Splits on \n or \r (Gradle's rich console progress bar uses \r to overwrite a line
 * in place, and we want each overwrite treated as a fresh line for marker/progress
 * parsing) while keeping any trailing partial line buffered for the next chunk.
 */
export class LineBuffer {
  private residual = '';

  push(chunk: string): string[] {
    const combined = this.residual + chunk;
    const parts = combined.split(/\r\n|\r|\n/);
    this.residual = parts.pop() ?? '';
    return parts.filter((l) => l.length > 0);
  }

  flush(): string[] {
    const rest = this.residual;
    this.residual = '';
    return rest.length > 0 ? [rest] : [];
  }
}
